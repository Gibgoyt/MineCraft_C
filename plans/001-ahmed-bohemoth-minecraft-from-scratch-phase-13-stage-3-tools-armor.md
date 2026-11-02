# 001-ahmed-bohemoth-minecraft-from-scratch — phase 13, stage 3: Tools and armor

**Status:** done   (updated 2026-11-03)

## Goal

Tools with durability and armor exist.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-items-survival.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/item/equipment/`
- `tests/run/item/equipment/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement durability and tool effects in `run/src/item/equipment/index.c`
- [x] Implement armor
- [x] Register test `run_item_equipment`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

Wiring outside the owned paths, as small as it could be:

- `run/{src,include}/item/index.*`, `run/src/item/README.md`: `equipment_t` takes the 48 bytes of `item_t`'s `_pad2`, and starts and ends with `item_open` and `item_close`. `item_t` stays 512 bytes.
- `run/src/game/index.c`, `run/src/game/README.md`: `game_interact_tick` sends the right button to `equipment_use_tick`, holds what the slot in hand is, and wears it for what broke; `game_report` has one more line.
- `plans/REPO_STRUCTURE.md`: a row for the test.

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_item_equipment$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Results, 2026-11-03: the build is clean; `run_item_equipment` passes, 26 tests in 26.4 s; `standards_lint` and `clang_format` pass. `run_item_inventory`, `run_item_crafting`, `run_player_interaction`, `run_entity_drop` and `run_game` still pass. Sampled with `nvidia-smi` during these runs, no process held more than 252 MiB of the card. Taking out the line that starts a block again on a change of tool makes the real-game test fail, at `ticks + IE_TICKS_SLACK >= IE_STONE_TICKS_HAND`, so that test does see the wiring.

## Commits

Appended as they land:

- `3f7d4b6` Add tools that wear and break, a hoe that tills, shears that bring out what they cut, and armor that goes on and stops hits.

## Issues found

**1. Gold tools mine at wood's speed, not their published 12 (left alone, out of this stage's paths).**
Expected: a gold pickaxe breaks stone at speed 12. Actual: speed 2. Evidence: `run/src/player/interaction/index.c`, `INTERACTION_TIER_SPEED` is indexed by `block_tier_t`, which has no gold, and `item_defs[].harvest_tier` for gold is `BLOCK_TIER_WOOD`, which is what `equipment_tool_held` hands over; `the_slot_in_hand_is_what_mining_goes_by_and_gold_breaks_what_wood_breaks` holds its tier. Done: nothing, since `run/src/player/interaction/` is not this stage's to edit and this is no wiring. The fix is a speed passed beside the tier through `player_interaction_hold_tool`, in that module and its test.

**2. A worn tool that lies on the ground comes back new (decided, and left for whoever may change `entity/item_drop`).**
`run/src/item/README.md` left this stage to decide. Expected: wear survives a spill. Actual: `game_spill` throws a worn stack as `item_drop_spawn(row, count, ...)`, `item_drop_t` has one spare byte where wear needs two, and `game_pickup_take` gets only an item and a count, through `inventory_add`. So a worn tool put in a chest and spilled by breaking the chest is repaired. Done: left as it is and written down in both READMEs, since a fix changes `item_drop_t`, its merge rule, its pickup function and `tests/run/entity/item_drop/`, none of them this stage's. Stage 13.4 needs the same fix if death spills the inventory.

**3. Shears give the block and its table's roll too, where the published game gives only the block (left alone, out of this stage's paths).**
Evidence: `player_interaction_tick` rolls `block_drops` in `run/src/player/interaction/index.c` before `game_tool_after_break` knows, so a shorn oak leaf can give a sapling as well. Done: left, and stated in the module's README. The extra is small: 5% a sapling.

**4. Armor has no toughness (a limit of the data, left alone).**
Expected: diamond's published toughness of 2 softens hard hits less. Actual: `config/items/` has no such field, so `equipment_armor_absorb` uses toughness 0 for all armor, and diamond stops a little less of a hard hit than published. Done: stated in the README. Adding it takes a field in `compile/src/game_tables/items/` and diamond's 4 files in `config/items/`, none of them this stage's.

**5. `run/src/player/interaction/README.md` is now out of date (left alone, out of this stage's paths).**
Its "What is left for others" still says only a hand exists in a real run until this stage, and that durability has no part there. Done: nothing here, as it is another stage's file. For Stage 18.3's audit.

**6. What a real run cannot reach yet.**
With no crafting screen until Stage 14.3, nothing in a played game puts a tool or armor in the inventory, and with no health until Stage 13.4 nothing calls `equipment_armor_hurt`; no combat until Stage 15.7 means the same for `equipment_tool_attacked`. All of it is tested directly, and tools and armor through the real threads with the inventory filled by the test.

## Gate check, 2026-11-03

The commit above says armor "stops hits". In a played game it did not.

- **Issue 7. Armor did nothing in a running game.** Expected: worn armor softens a hit and wears for it. Actual: `grep -rn 'player_survival_armor_set\|equipment_armor_hurt' run/src` found only their own definitions. `survival->armor_points` stayed 0 for ever and no piece ever lost a use. Stage 13.4's issue 3 left the one call to whoever merged the two stages, and merge `2d74762` did not add it. Tests passed because each module was tested alone. Fixed: `game_survive_tick` sets the points and toughness worn before each tick, and hands what armor stood against (`player_survival_armor_hit_take`, new) to `equipment_armor_hurt` after it. New test: `in_a_real_game_armor_stops_hits_and_wears_and_what_goes_past_it_does_not_touch_it`, with an outside hit, real lava, being alight, and a piece breaking.
- **Issue 1 is fixed: gold mines at 12.** `equipment_tool_speed_milli` and `player_interaction_hold_speed` carry it past the tier, and changing to it starts a block again. Gold on stone is 4 ticks, against wood's 23; held by `gold_mines_faster_than_diamond_and_harvests_only_what_wood_does` and through the real game. A gold sword mines as a sword.
- **Issue 2 is fixed: wear survives the ground.** `item_drop_t` has `wear` (`pickup_delay` became a `uint8_t` to make room, and the struct stays 64 bytes), only the same wear merges, `item_drop_take_t` is told it, and `game_pickup_take` puts it back. New tests in `run_entity_drop` and `run_item_equipment`, through a broken chest and through dying.
- **Issue 3 is fixed: shears give only the block.** `interaction_mine` rolls nothing for shears on their own block. 200 cuts of tall grass give no rolled drop, where a hand's do.
- **Issue 4 is fixed: armor has toughness.** `armor: { toughness: 2 }` on diamond's 4 files, read by `compile/src/game_tables/items/`, in `item_def_t` from its padding, and in both sums. `toughness_keeps_hard_hits_out_and_both_sums_are_one_sum` holds the two armor sums, in `run/src/item/equipment/` and `run/src/player/survival/`, to each other over every points, toughness and hit of up to 50.
- **Issue 5 is fixed:** `run/src/player/interaction/README.md` says what is true now.
- **Issue 8. A press that woke a body or brought it back also used what was in hand.** `equipment_use_tick` runs before survival takes the buttons, so asleep with a helmet in hand, the press that woke it also put the helmet on, and a hoe would till. Fixed in `game_interact_tick`; `a_press_that_wakes_or_brings_back_a_body_does_nothing_else`.
- **Still as issue 6 says:** no combat until Stage 15.7, so `equipment_tool_attacked` and `equipment_attack_damage` are reached only by tests.
