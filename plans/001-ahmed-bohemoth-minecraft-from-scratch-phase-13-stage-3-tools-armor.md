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
