# 011-ahmed-bohemoth-farmers-workbench — master plan

**Status:** draft, not started (2026-11-14). **This plan is for later**, as Ahmed asked: it is written now so the task is known, and nothing here starts until he says go. Plans only: on approval, `plans/README.md` applies. This file is the master, kept verbatim from here on, and the phase files in section 9 carry the work. Nothing is pushed.

## 0. Context

### Ahmed's words, 2026-11-14

- "For tree feller I should never just cut on block and the entire tree falls. I have to cut a layer. Like in Hytale ... I have to cut all the blocks that have blocks on top of it, and I leave a stump in the ground in Hytale I really really love that feature."
- "Maybe also add plans to create a farmers workbench and separate that entirely from the woodworking bench??? add a phase in the plan for that too please! There are actually loads of game logic that Hytale has for trees but we will not be doing that right now! Only be doing the tree feller stuff I think."
- "Trees must also have an underground root and not just start on top of dirt ... Look at ~/Pictures/10.png in Hytale the tree only falls if i cut down an entire layer! and 11.png shows the tree stump, i can tell you that a little of that stump goes underground."
- "So we are touching world generation to make trees have a tree stump as well as properly creating tree feller!"
- "I think for farming bench put that at ./plans/011-* so that I know that we are doing that task later."

The tree feller and the stump are plan 010 (`plans/010-ahmed-bohemoth-initial-game-fixes-round-3.md`). **This plan is the farmer's workbench, on its own, for later.**

### The pictures, for the record

Ahmed's pictures for plan 010 are about trees:
- `~/Pictures/10.png`: a 2 by 2 trunk with a notch cut in its bottom layer, still standing.
- `~/Pictures/11.png`: the stump left level with the grass after the fall.

They show nothing of a farming bench. The bench's look is ours (section 2.2).

### The idea, in our own terms

A **farmer's workbench**: a station of its own where what is grown is worked. It is **entirely separate from the woodworking bench**: its own block, its own item, its own recipe to make it, its own station in the recipe files and its own screen. No farming recipe is made at the woodworking bench, and no woodworking recipe at the farmer's.

Background reading (allthings.how, on regrowth in another game) says only that such a game has a separate farming bench where plant things are made, regrowth among them. **Regrowth, saplings from the bench, growth essences and watering are out of scope** (section 8). This plan builds the station and moves the recipes that belong there.

### What exists to build on (read from the code, 2026-11-14)

- **Stations are data plus a few rows:**
  - `config/recipes/README.md`, "Stations": "A new station is one word here, one word in `GR_STATIONS` (or `GR_PROCESSES` for a smelt) in `compile/src/game_tables/recipes/`, its block, and its screen's row in `run/src/ui/`".
  - `GR_STATIONS` is ordered as saves wrote it, "so a new station goes on the end" (`compile/src/game_tables/recipes/index.c:44-55`): table 0, woodworking 1, stoneworking 2, smithing 3, leatherworking 4, hands 5. **Farming is 6.**
- **A two-part bench has a pattern to copy:** the leather workbench (plan 005 phase 06):
  - blocks `leather_workbench_left` and `_right`, each `facing: horizontal`, each dropping the item;
  - item `leather_workbench` placing `leather_workbench_left`;
  - procedural textures under `textures/blocks/leather_workbench_*` and `textures/items/leather_workbench`;
  - a row in `BENCH_SHAPES` (`run/src/world/bench/index.c:19`);
  - a `UI_SCREEN_LEATHERWORKING` kind (`run/include/ui/screen_inventory/index.h:43`) with its row in `UI_SCREEN_KINDS` and in `ui_screen_kind_of_block` (`run/src/ui/index.c:189`, `:354`);
  - tests in `tests/run/item/crafting/station/`, `tests/run/player/interaction/bench/`, `tests/run/ui/screens/`, `tests/cuda/shapes/` (`:888`).
- **The bench grid is saved** with its station (`run/src/save/state/index.c:226`, `save_state_check_station_grids` at `:616`), so the check must accept the new station.
- **What farming has today:** wheat (`wheat_stage_1` to `_8`, `farmland`, `wheat_seeds` from tall grass 1 in 8, `wheat`), melons (`melon`, `melon_slice`, `melon_seeds`), pumpkins (items only: "No pumpkins in the world yet", `run/src/terrain/feature/README.md`), potatoes and carrots (items), bone and bone meal, saplings of 8 woods, and hoes of every material.

### The recipes as they stand (every `config/recipes/*/index.yaml`, 2026-11-14)

| Recipe | Station today | Farming? |
|---|---|---|
| `melon` (9 slices into a melon) | **off**: "Nine slices take a 3 by 3, which the hands do not have, and no station makes food" | Yes: a grown crop worked into its block |
| `melon_seeds` (a slice into seeds) | hands | Yes: seeds from a crop |
| `pumpkin_seeds` (a pumpkin into 4 seeds) | hands | Yes: seeds from a crop |
| `bone_meal` (a bone into 3) | hands | Yes: what makes crops grow |
| `bread`, `cookie`, `pumpkin_pie`, `mushroom_stew` | hands | Food from crops: eating, not farming |
| `sugar` (from sugar cane) | hands | A cooking ingredient |
| `paper` (3 sugar cane) | woodworking | A craft good |
| `dye_*` from flowers | hands | Dyes |
| `wood_hoe`, `stone_hoe`, metal hoes | woodworking, stoneworking, smithing | Farming tools, but tools are made by their material |
| `bowl` | woodworking | Wooden |

None of today's woodworking recipes is farming apart from the hoe, so separating the two benches means **giving farming its own station**, not taking much from the woodworking bench.

## 1. What changes, in one paragraph

A new two-part station, the **farmer's workbench** (`farmers_workbench_left` and `_right`, 2 by 1 by 1, like the leather workbench), is made at the crafting table and has a 3 by 3 grid. Its station in the recipe files is **`farming`**, and its screen is titled "Farmer's Workbench". Four recipes move or come on there:
- `melon` turns **on**: the bench has the 3 by 3 it lacked;
- `melon_seeds`, `pumpkin_seeds` and `bone_meal` move from the hands.

Food, sugar, dyes, paper and every hoe stay where they are. The unlock order gains a step: hands → table → **farmer's workbench**, from logs and seeds.

## 2. Design

### 2.1 The station in the recipe files

| Where | What |
|---|---|
| `compile/src/game_tables/recipes/index.c` | `GR_STATION_FARMING = 6`, `GR_STATION_COUNT = 7`, `"farming"` on the end of `GR_STATIONS` |
| Generated `tables/recipes/` | `RECIPE_STATION_FARMING`, from the tool |
| `config/recipes/README.md` | The station row, the field table's list of stations, the unlock order, the "departs from the published recipes" rows for the moved recipes |
| `run/src/save/state/index.c` | `save_state_check_station_grids` accepts a bench grid of station `farming`; a saved bench grid of any older station still opens |

### 2.2 The block, the item, the pictures

- `config/blocks/farmers_workbench_left/index.yaml` and `_right`:
  - hardness 2.5, tool axe, sound wood, `facing: horizontal`;
  - each drops `{ item: farmers_workbench, count: 1 }`;
  - top, bottom, end, and north and south pictures as the leather workbench lays them out.
- `config/items/farmers_workbench/index.yaml`: places `farmers_workbench_left`.
- **Our own pictures**, procedural recipes under `textures/blocks/farmers_workbench_*` and `textures/items/farmers_workbench`, painted by `compile/src/texture_vector`:
  - a plank top with a soil trough along its back and two small seed boxes;
  - legs and a brace at the ends;
  - a hanging bundle of grain on its front.
  - No pack's pixels and no other game's art: `source.kind: procedural`, each `README.md` saying so.
- `run/src/world/bench/index.c`: a `BENCH_SHAPES` row `{farmers_workbench_left, farmers_workbench_right}`, `right = {0, 1}`, count 2. Its README table gets a row.

### 2.3 The screen

- `UI_SCREEN_FARMING = 11`, `UI_SCREEN_KIND_COUNT = 12` (`run/include/ui/screen_inventory/index.h`).
- `UI_SCREEN_KINDS[UI_SCREEN_FARMING] = {.title = "Farmer's Workbench", .works = UI_SCREEN_WORKS_BENCH, .station = RECIPE_STATION_FARMING}`.
- It uses the crafting places and drawing that woodworking, stoneworking and leatherworking share (`ui_screen_places_of`, the draw switch).
- `ui_screen_kind_of_block` gets both parts.
- The bench grid it works is `item_t`'s shared `borrowed_crafting_bench`, as every bench's is.

If `UI_SCREEN_KIND_COUNT` is saved or sized anywhere, the phase finds it and records it.

### 2.4 The recipe to make it

At the crafting table, which "makes only the other stations":

```yaml
# config/recipes/farmers_workbench/index.yaml
name: farmers_workbench
kind: shaped
pattern: ["SSS", "L L"]
keys: { S: group_seeds, L: group_logs }
gives: farmers_workbench
station: table
```

**`group_seeds`** is a new group: `wheat_seeds`, `melon_seeds`, `pumpkin_seeds`. Why seeds and logs:
- Logs are the first thing a player has.
- Wheat seeds come from tall grass (1 in 8), so the bench is reached on the first day, after the woodworking bench, from what a farmer starts with.
- The pattern matches no other table recipe:
  - the woodworking bench is `LLL`;
  - the stoneworking bench is `CCC / L L`;
  - the leather workbench is `PPP / S S`;
  - the tanning rack is all sticks;
  - the smith table is `III / SFS`.

The tool refuses two recipes one grid could match, so a clash is a build failure (`config/recipes/README.md`).

### 2.5 What moves, and what stays

| Recipe | From | To | Why |
|---|---|---|---|
| `melon` | off | **`farming`** | Nine slices need a 3 by 3, and its `off` reason was that no station makes it; now one does |
| `melon_seeds` | hands | **`farming`** | Getting seed out of a crop is the farmer's work |
| `pumpkin_seeds` | hands | **`farming`** | The same |
| `bone_meal` | hands | **`farming`** | Making what makes crops grow |
| `bread`, `cookie`, `pumpkin_pie`, `mushroom_stew` | hands | stays | Eating must not wait for a bench; Ahmed's order of 2026-11-10 keeps "simple food" in the hands (Q2) |
| `sugar` | hands | stays | An ingredient for cooking (Q2) |
| `dye_*` | hands | stays | Dyes, not farming |
| `paper` | woodworking | stays | A craft good (Q3) |
| `bowl` | woodworking | stays | Wooden |
| every hoe | its material's station | stays | "Everything wooden and the wooden tools" at woodworking, stone tools at stoneworking, metal at the smith table: tools are made by material (Q4) |

The hands lose 3 recipes. That changes Ahmed's 2026-11-10 order for the hands ("dyes, bone meal, sugar, seeds, simple food"), so it is **Q1**, asked before phase 02.

### 2.6 The unlock order

`config/recipes/README.md`'s order gains, after step 2 (the woodworking bench):

> **Table:** 3 seeds of any kind over 2 logs make the farmer's workbench. **Farmer's workbench:** seeds from melons and pumpkins, bone meal from bones, a melon from nine slices.

`run_item_crafting_station`'s `every_recipe_is_reached_from_what_the_world_gives_walking_the_stations_in_order` walks it. `IS_STATION_ITEM[RECIPE_STATION_FARMING] = ITEM_ID_FARMERS_WORKBENCH` and `IS_TABLE_MAKES` gain the bench.

## 3. What does not change

- The woodworking bench: its block, recipe, screen and recipes, all but none (no farming recipe was there besides the hoe, which stays).
- Every other station and smelt.
- Crops, farmland, growth, hoes' use, bone meal's use in the world.
- The land, the save format (beyond the grid check accepting a new station number), the generator version.
- The tree feller and stumps (plan 010).

## 4. Costs and limits

None that matter at runtime: a table row, a shape row, a screen row. The texture recipes add 7 small pictures. The tables' sizes and the `RECIPE_STATION_COUNT`-sized arrays grow by one entry.

## 5. Tests the plan relies on

- **The station:** the table makes the farmer's workbench from 3 seeds over 2 logs, and nothing else makes it.
- **Placing and breaking:** the item places both parts, facing the placer. Mining or blowing up any part drops one bench. The right button on either part opens "Farmer's Workbench" (`tests/run/player/interaction/bench/`, `tests/run/ui/screens/`).
- **Drawn:** both parts in `tests/cuda/shapes/`'s row of stations, looked at.
- **Recipes:**
  - each moved recipe is made at `farming` only, and the hands or any other station make nothing of that pattern;
  - `melon` is on;
  - `each_recipe_is_made_at_its_own_station_only_and_nothing_in_two_places` holds.
- **Separate from the woodworking bench:** no recipe of station `farming` matches at woodworking, and none of woodworking's matches at farming.
- **The order:** every recipe is still reached from what the world gives, walking the stations in the new order.
- **Saves:** a world saved with the bench grid on `farming` reopens with it. A world saved before this plan opens.

## 6. Risks

| Risk | What's done about it |
|---|---|
| Moving seeds and bone meal out of the hands slows a new player who wants melons | The bench is cheap and comes on day one; Q1 asks Ahmed first |
| `group_seeds` with pumpkin seeds that no one can get yet | Wheat seeds alone make it; the group is for later crops too |
| A pattern clash with a later table recipe | The tool refuses two recipes one grid could match |
| `run_playthrough` crafts something now moved | It makes planks, a table, the woodworking bench, stairs and wooden tools; none moves. Checked in phase 02 |
| A UI or save array sized by a count that grows | Each found and recorded in phase 01 |

## 7. Old worlds

None affected. The land does not change, and the generator version stays (whatever plan 010 set it to). A saved bench grid keeps its station number, since farming goes on the end.

## 8. Out of scope

Ahmed: "There are actually loads of game logic that Hytale has for trees but we will not be doing that right now!" So:
- saplings crafted at the bench, and regrowth of trees from them;
- a growth essence or any crop-to-currency conversion;
- watering cans, seed bags, planters, sprinklers;
- new crops (potatoes and carrots in the ground), and pumpkins in the world;
- a new farming tool or a hoe recipe moved to the bench (Q4);
- anything of plan 010 (the feller, stumps, roots).

## 9. Phases

Each phase ends somewhere worth stopping, and becomes one phase file with the template's Goal, Todo, Verification, Commits and Issues found. Every phase file also gets the richer parts plan 006 uses:
- read first;
- must NOT build;
- owned paths;
- steps with a *Why*;
- tests with numbers;
- the gate;
- risks and open questions.

Every heavy command goes through `tools/test-or-build.sh`. **No phase starts until Ahmed says go.**

### Phase 00: Vision and plan files

- [ ] This master in `plans/011-ahmed-bohemoth-farmers-workbench.md`, committed alone
- [ ] Phase files 01 to 03 from the template, every box unticked, status `not started`, each linking back to phase 00, one commit each

### Phase 01: The farmer's workbench station

- [ ] `farming` in `GR_STATIONS` (on the end); `RECIPE_STATION_FARMING`; the save check accepts it
- [ ] Blocks `farmers_workbench_left` and `_right`, item `farmers_workbench`, procedural pictures
- [ ] `BENCH_SHAPES` row; `UI_SCREEN_FARMING`; `ui_screen_kind_of_block`
- [ ] `group_seeds` and the `farmers_workbench` recipe at the table
- [ ] `melon` turned on at `farming`, as the station's first recipe
- [ ] Tests: station, placing and breaking, screen, shapes, save; READMEs

**Gate:** Ahmed makes the bench from seeds and logs, places it, opens it, and makes a melon from nine slices.

### Phase 02: Farming recipes move to the bench

- [ ] Ahmed's answer to Q1 recorded; then `melon_seeds`, `pumpkin_seeds`, `bone_meal` move to `farming`
- [ ] `config/recipes/README.md`: stations table, hands' line, unlock order, departures table
- [ ] `run_item_crafting_station`: the order walk, the separation test, nothing in two places
- [ ] `run_playthrough` unaffected, checked

**Gate:** Ahmed finds he can no longer make bone meal in his hands, and can at the farmer's workbench.

### Phase 03: The final check

- [ ] Full debug and release suites; a release build
- [ ] Ahmed's gate notes, in his words

## 10. Still open for Ahmed

Each has a default, and none blocks approval.

| # | Question | Default if not answered |
|---|---|---|
| Q1 | Move `melon_seeds`, `pumpkin_seeds` and `bone_meal` out of the hands, changing the 2026-11-10 order? | Yes, to the farmer's workbench |
| Q2 | Food from crops (`bread`, `cookie`, `pumpkin_pie`, `mushroom_stew`) and `sugar` to the bench too? | No: eating stays in the hands |
| Q3 | `paper` to the bench? | No: a craft good, at woodworking |
| Q4 | Hoes to the bench? | No: tools by material |
| Q5 | The recipe: 3 seeds over 2 logs? | Yes |
| Q6 | Size: 2 by 1 like the other benches? | Yes |

## Verification of this plan

    ls plans/011-*
    grep -L "## Issues found" plans/011-*phase-*.md   # prints nothing
    git status --short                               # nothing uncommitted
