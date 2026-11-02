# 001-ahmed-bohemoth-minecraft-from-scratch — phase 13: Items and survival

**Status:** in progress   (updated 2026-11-03): the gate has passed. What awaits Ahmed: a decision on a bed recipe, and the number keys and wheel on real input. See Issues found.

## Goal

Inventory, crafting, smelting, tools, armor, health and hunger all work.

**Wave:** W8.

**Stages:**

- `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-stage-1-item-inventory.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-stage-2-crafting-furnace-chest.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-stage-3-tools-armor.md`
- `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-stage-4-health-hunger.md`

## Todo

- [x] Stage 13.1: Items and inventory
- [x] Stage 13.2: Crafting, furnace and chest
- [x] Stage 13.3: Tools and armor
- [x] Stage 13.4: Health and hunger
- [x] Phase gate passed

## Verification

**Gate:** All four stages pass their tests. The plan sets no separate gate for this phase.

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(run_item|run_player_survival)' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `bf9dcec` Make armor stop hits and wear in a running game, spill it on death, keep wear on the ground, and give gold, shears and diamond their published numbers.
- `7b26962` Record the Phase 13 gate results and list the crafting test in the repo structure.

## Issues found

Gate check of 2026-11-03, from a clean build of the integration branch. All the listed tests passed as they stood, and the defects below were not seen by any of them. Each is written up in full in its stage file.

| Defect | Evidence | Done |
|---|---|---|
| Armor did nothing in a running game, though Stage 13.3's commit says it stops hits | No caller of `player_survival_armor_set` or `equipment_armor_hurt` in `run/src` | Fixed, Stage 13.3 issue 7 |
| Death left armor on, and a chest or death repaired worn tools | `game_death_spill`, `game_spill`, and no `wear` in `item_drop_t` | Fixed, Stage 13.4 issue 14 and Stage 13.3 issue 2 |
| Gold mined at wood's speed, shears gave a table's roll as well, and armor had no toughness | Stage 13.3 issues 1, 3 and 4, left open there | Fixed |
| A press that woke a body also used what was in hand | `equipment_use_tick` ran first | Fixed, Stage 13.3 issue 8 |
| `run_item_crafting` failed on the sanitize preset | Misaligned `arena_t`, in the test | Fixed, Stage 13.2 issue 9 |

Reached only by tests, on purpose, until Stage 14.3 has screens: `crafting_grid_*`, `container_*` but for what `item_world_tick` calls, and `smelting_click` and its quick moves. A played game has no way to craft or to open a chest or furnace yet. Stage 15.7 is the same for `equipment_tool_attacked` and `equipment_attack_damage`.

**Results, from a clean build of this branch.** The build is clean. Gate: `run_item_crafting`, `run_item_equipment`, `run_item_inventory` and `run_player_survival` all pass, and `standards_lint` and `clang_format` pass. `pgrep -a minecraft_c` finds none. On the sanitize preset, `run_item_crafting`, `run_item_equipment`, `run_item_inventory`, `run_player_survival` and `run_entity_drop` pass. Taking out the armor's wear and a drop's wear makes 3 of the new real-game tests fail, so those tests do see the wiring.

**The full suite, none of it on this phase's gate.** `ctest --preset debug -j3` passed 76 of 78, with others on the card. `render_feature_sky` timed out at 280 s, as Stage 13.2's issue 6 and Stage 13.4's issue 11 record, and passes alone in 157.5 s. `run_render_frame` failed `the_real_threads_draw_what_one_thread_draws_with_nothing_from_the_heap` at `columns_made > 100u` and at byte 24945 of the picture, the same failure Stage 12.2 and Stage 13.2's issue 6 record, in a game with no `item_t`. An earlier run at `-j4` passed 75 of 78, with those two and `run_render_upload` failing; its message was not kept. It is new to these records. Alone, `run_render_frame` and `run_render_upload` each passed 3 of 3. Left alone as not this phase's, and worth a look by whoever owns the renderer's tests: they do fail under load.

**Awaits Ahmed:**

1. No bed can be got in a played game: `config/recipes/README.md` gives it no recipe for want of wool, and there is no wool. A recipe with none would be invented, so it is his to decide. Everything else about the bed is tested.
2. The number keys and the wheel on real input, which no test here may open. Stage 13.1 issue 5.
