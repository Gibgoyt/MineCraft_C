# 001-ahmed-bohemoth-minecraft-from-scratch — phase 12, stage 4: Dropped items

**Status:** in progress   (updated 2026-11-02). Every check an agent can run has passed. Waiting for Ahmed's check with a real mouse, the one unticked box.

## Goal

The dropped-item entity, its physics, merging and pickup exist.

**Wave:** W7. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-12-player-mining.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/entity/index.*` and `README.md` (the Owner tier)
- `run/{src,include}/entity/item_drop/`
- `tests/run/entity/item_drop/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create the `entity` Owner tier: `run/src/entity/index.c` with paired `entity_init` and `entity_deinit`, owning the regions and arenas its parts borrow
- [x] Implement the entity and physics in `run/src/entity/item_drop/index.c`
- [x] Implement merging and pickup
- [x] Register test `run_entity_drop`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each
- [x] Extend the scripted proof: dirt drops, is picked up, and is placed back
- [ ] Ahmed: on a text console, mine dirt, walk over the item, and place it back with a real mouse. Only he can run this: see Verification

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_entity_drop$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

    # Ahmed runs on a text console (Ctrl+Alt+F3, log in); agents cannot verify this, because the desktop owns the display.
    tools/test-or-build.sh cmake --build --preset release
    ./build/release/bin/minecraft_c --test-world --walk

## Verification results

- `cmake --build --preset debug`: builds. `standards_lint: clean`, `no recursion`.
- `ctest --preset debug -R '^run_entity_drop$'`: passes, 27 tests. On the sanitize preset: passes with no report, as does `run_player_interaction`.
- `ctest --preset debug -R '^standards_lint$'` and `'^clang_format$'`: pass.
- `ctest --preset debug`, all of it: 68 of 70 pass. `run_types` failed on two lines that count the heap's regions, and passes since they went from 9 to 10: Issue 2. `run_render_frame` fails only in `golden_pictures_match`, with the same three numbers as Stage 12.3's Issue 4 (53247, 55941, 54263), so not from this stage. The 9 tests nearest this stage were run again after the last change and pass. The whole suite was not run again.
- `run_player_interaction`, 5 more times, with its real threads: 0 failures.
- Eight deliberate bugs built in at once (gravity 0.05, ice as stone, the grid looking in one layer, the wait one tick short, despawn one tick late, a cactus harmless, a merge keeping the older age, and `game` giving nothing to the hand): 9 tests failed, including one for each bug. The sources were then restored from a copy, rebuilt, and the test passes.
- A store of 4096 stacks, all moving, in the debug build: 4.38 ms a tick, of the 50 ms there are.
- `pgrep -af "$PWD/build"`: nothing of this worktree left running.
- **Scripted headless play,** at 960x540, with the script in `di_script`: the program reports `the player mined 1 blocks and placed 1; the last mined was grass_block, and the hand holds 0 of nothing` and `1 items dropped, 1 picked up, 0 on the ground in 0 stacks`, starting at and ending at z = 85, with `0 bytes live`. The test stops the same script at four points and reads the report at each:

| Stopped at tick | Mined | On the ground | Picked up | Hand | Placed |
|---|---|---|---|---|---|
| 20 | 0 | 0 | 0 | nothing | 0 |
| 27 | 1, grass | 1 | 0 | nothing | 0 |
| 44 | 1, grass | 0 | 1 | 1 of dirt, body at z = 84, in the hole | 0 |
| 95 | 1, grass | 0 | 1 | nothing, body back at z = 85 | 1 |

  By eye: `before.png` shows grass all over, `hole.png` a hole with dirt in it, and `back.png` the same picture as the first with one square of dirt where the grass was. Twice through at 160x90 gives the same bytes.
- **Not checked:** real input and KMS, which only Ahmed can run. Items cannot be seen in any picture: Issue 1.

## Commits

Appended as they land:

- `bb8a800`: the entity Owner, the item_drop part, the wiring, the test, and the docs.

## Wiring outside the owned paths

The smallest needed to call this stage's module, as the task allows:

- `run/{src,include}/player/interaction/index.*`: `player_interaction_hand_take` replaces `player_interaction_drops_to_hand`, as Stage 12.3's Issue 1 said it would. `picked_up_count` takes the place of `drops_unheld_count`, so the struct keeps its size.
- `run/{src,include}/player/index.*`: `player_pickup_reach`, and `player_interact_tick` no longer hands drops to the hand.
- `run/{src,include}/game/index.*`: `game_entity_set`, `game_items_tick` in `game_sim_tick`, the open and close, and a line in `game_report`. `game_t` keeps its size.
- `run/{src,include}/main/index.*`: `entity_t` in `app_t`, 10688 to 10880, with its init and deinit.
- `tests/run/player/interaction/index.c`: `pi_drops_to_hand`, a test-side stand-in, since those tests are of the hands; a rig with an entity for the real threads; the one-slot test rewritten for `player_interaction_hand_take`.
- `tests/run/types/index.c`: Issue 2.
- The READMEs of `run/src/player/`, `run/src/player/interaction/`, `run/src/game/`, `run/src/main/`, `tests/run/player/interaction/`, and `plans/REPO_STRUCTURE.md`.

## Issues found

### Issue 1: items are not drawn

- **Expected:** a player sees what fell.
- **Actual:** `cuda/src/entity_rays/` is Stage 09.5's, whose file says `not started`. An item does everything but show.
- **Done:** left to Stage 09.5. The positions are in `item_drop_store_t`, on `sim`: it will need a snapshot to `gpu`. The proof goes by what the program reports, and by the world's own pictures. Since the item cannot be seen, Ahmed's check has to go by the hand: placing works once it is picked up.

### Issue 2: `run_types` counted the heap's regions by number

- **Expected:** a new Owner breaks nothing.
- **Actual:** `tests/run/types/index.c:914`: `does not contain "heap at exit: 9 reserved, 9 released, 0 bytes live"`, with `10 reserved, 10 released`. Line 922 the same for `leak at exit`.
- **Done:** both numbers changed to 10, in that test, as `entity_init` reserves one region on every run. Every new Owner with a region will meet this.

### Issue 3: a hand of one slot leaves other items on the ground

- **Expected:** what is mined can be picked up.
- **Actual:** Stage 12.3's `player_interaction_hand_give` replaces a different block, which picked up from the ground would throw away what the hand holds. Coal and other items that are no block have nowhere to go at all.
- **Done:** `player_interaction_hand_take` takes none of those, so they stay on the ground, and despawn after 5 minutes. A player holding dirt can pick up cobblestone only after placing all the dirt. A rule of the stand-in, and it goes with Stage 13.1's inventory.

### Issue 4: what water washes away still gives no drop

- **Expected:** with items now in the world, what `world/tick` washes away would drop, as `run/src/world/tick/README.md` line 62 says it should once this stage lands.
- **Actual:** only the player's mining makes items. `world/tick` has no road to `entity`, and that line of its README is now out of date.
- **Done at first:** left alone and reported, as it is outside this stage's paths and more than wiring.
- **Done at the gate check, 2026-11-02:** fixed. `world/tick` keeps a ring of 64, read with `world_tick_broken_next`: what water washes away, what a falling block buries, leaves that decay, and a crop with no farmland under it. Lava gives nothing. `game_items_tick` rolls each one's table and spawns it. `world_tick_t` goes from 704 to 728 bytes, out of `world_t`'s padding, whose size stays 2048, and `sim_t` in `tests/run/world/tick/` does the same. Tests: `the_tick_says_what_water_washed_away_and_what_sand_buried_and_lava_gives_nothing`, `a_full_ring_of_what_went_by_itself_loses_the_oldest_and_counts_them`, and `in_a_real_game_water_washes_a_plant_away_and_it_comes_out_as_an_item` on the real threads. Taking the roll out of `game_items_tick` fails the last (`items->dropped_count == 1u: 0 != 1`), and letting lava give a drop fails the first.
- **Still open:** flowing water does not carry an item, so `item_drop` has no way yet to be told about flow. The plan's Stage 12.4 does not ask for it.

### Issue 5: the first version of pushing an item out of a block did not always end

- **Expected:** an item buried in a block comes out.
- **Actual:** `when_what_it_lies_on_goes_it_falls_and_when_a_block_comes_down_on_it_it_leaves` failed in two ways. A sweep stopped a buried item at the next block over: `buried->position[2] >= 9.0`. Then, going through, with its middle in an open cell and its box still over the block below, it went back and forth between two open cells for good, as each was the other's nearest open face: `physics_box_is_clear` stayed false after 200 ticks.
- **Done:** fixed. It goes straight through while its middle is in a solid cell, and with its middle in an open cell it moves wholly into that cell, which ends it. The test covers a diagonal neighbour as well.

### Issue 6: a stack that had stopped at the edge of what is loaded froze

- **Expected:** it stops as at a wall, and goes on ageing.
- **Actual:** `going->age == 100u: 6 != 100`. Its box's face was exactly on the line, so the corner's cell counted as not loaded.
- **Done:** fixed: a box only touching the edge is not over it.

### Issue 7: `item_drop_box` and `player_pickup_reach` returned a 48-byte struct

- **Done:** fixed at the gate check, 2026-11-02, with Stage 12.1's Issue 7, which has the details.
