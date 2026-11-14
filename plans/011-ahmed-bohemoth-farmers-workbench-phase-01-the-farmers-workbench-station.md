# 011-ahmed-bohemoth-farmers-workbench — phase 1: The farmer's workbench station

**Status:** not started   (updated 2026-11-14). Waits for Ahmed's go on plan 011.

Read first: `plans/011-ahmed-bohemoth-farmers-workbench-phase-00-vision-and-plan-files.md`, then the master `plans/011-ahmed-bohemoth-farmers-workbench.md` (sections 0, 1, 2.1 to 2.4, 5, 6 and 9), then `plans/005-ahmed-bohemoth-initial-game-fixes-round-2-phase-06-crafting-stations.md` for how the stations were first made.

## Goal

The farmer's workbench exists as a station of its own, entirely separate from the woodworking bench:
- **Made:** at the crafting table from 3 seeds of any kind (`group_seeds`: wheat, melon, pumpkin) over 2 logs.
- **Placed:** as two blocks, `farmers_workbench_left` and `_right`, lying across the view as every two-part bench does.
- **Broken:** mining or blasting any part drops one bench.
- **Opened:** the right button on either part opens a 3 by 3 grid titled "Farmer's Workbench", of station `farming`.

Then:
- `farming` is station number 6, on the end of `GR_STATIONS`, so every saved grid keeps its number. The save's grid check accepts a bench grid on `farming`.
- The pictures are our own, painted by recipe.
- `melon` is turned on there, the station's first recipe: nine slices make a melon, at the bench only.
- No other recipe moves yet (phase 02).

## Where this fits

**The vision.** Ahmed: "create a farmers workbench and separate that entirely from the woodworking bench". This phase is the "separate" part: a station that shares nothing with woodworking but the shared bench grid in `item_t`, as every bench does.

**What later phases read from it:** phase 02 moves recipes onto `farming`; phase 03 checks the whole.

**What this phase must NOT build:**
- No recipe moved from the hands or any station (phase 02, after Q1).
- No saplings, regrowth, essence, watering can or seed bag (master section 8).
- No change to the woodworking bench, its recipes or its screen.
- No new crop in the world.
- No other game's names, art or text.

## Ahmed's words

- "Maybe also add plans to create a farmers workbench and separate that entirely from the woodworking bench??? add a phase in the plan for that too please!"
- "I think for farming bench put that at ./plans/011-* so that I know that we are doing that task later."

---

## Background, explained

### Every place a station is named today

Read from the code, 2026-11-14, by following `leather_workbench` and `LEATHERWORKING` through the tree:

| Place | What it holds |
|---|---|
| `compile/src/game_tables/recipes/index.c:44-55` | `gr_station_t` and `GR_STATIONS`, "the order saves were written in, so a new station goes on the end" |
| `config/recipes/README.md` | The `station` field's list, the stations table, the unlock order |
| `config/blocks/leather_workbench_left/`, `_right/` | `facing: horizontal`, drops the item, six pictures |
| `config/items/leather_workbench/` | `block: leather_workbench_left` |
| `textures/blocks/leather_workbench_*` (6), `textures/items/leather_workbench` | Procedural recipes |
| `run/src/world/bench/index.c:19` | Its `BENCH_SHAPES` row, `right = {0, 1}`, count 2 |
| `run/include/ui/screen_inventory/index.h:43` | `UI_SCREEN_LEATHERWORKING = 8`, `UI_SCREEN_KIND_COUNT = 11` |
| `run/src/ui/index.c:189`, `:226`, `:304`, `:354` | Its `UI_SCREEN_KINDS` row, places, draw, and the block-to-screen table |
| `run/src/save/state/index.c:610-620` | `save_state_check_station_grids`: which stations a saved bench grid may be |
| `tests/run/item/crafting/station/index.c:16-32` | `IS_STATION_ITEM`, `IS_TABLE_MAKES` |
| `tests/run/player/interaction/bench/` | Two-long stations lie across the view; each half finds the other |
| `tests/run/ui/screens/` | `a_block_opens_only_its_own_screen`, the slots of each screen |
| `tests/cuda/shapes/index.c:871-889` | The stations drawn in a row, with a golden |
| `run/src/item/crafting/README.md` | The bench grid's list of stations |

Each needs a farming row.

### Why seeds over logs

The table makes only stations, so the bench is made there.
- Logs are the first thing a player has.
- Wheat seeds come from tall grass, 1 in 8 (`config/blocks/tall_grass/index.yaml`).
- So the bench is reached on day one, by a player who has started to farm.

No table recipe has this shape (master 2.4), and the tool refuses a clash at build time.

## Design

As the master's 2.1 to 2.4:
- `GR_STATION_FARMING = 6`, `"farming"`;
- the blocks and item;
- 7 procedural pictures: top left, top right, bottom, end, side left, side right, and the item;
- the `BENCH_SHAPES` row;
- `UI_SCREEN_FARMING = 11`, using the crafting places and draw;
- `group_seeds`;
- `config/recipes/farmers_workbench/index.yaml`.

`config/recipes/melon/index.yaml` loses its `off:` line and gains `station: farming`.

**The pictures** (one recipe each, `compile/src/texture_vector/recipe/README.md`'s operations):
- **Top:** planks, with a dark soil trough along the back third and two small seed boxes.
- **Ends:** legs and a brace, as the leather workbench's end.
- **Front:** a bundle of grain hanging from the apron.
- **Item:** the bench seen from above.

Colours are greens and straw over the planks' brown, so it reads apart from the woodworking bench at a glance. Each `README.md` says "Our own: painted by the recipe".

## Owned paths

This phase may edit these, plus this file:
- `compile/src/game_tables/recipes/index.c`
- `config/blocks/farmers_workbench_left/`, `config/blocks/farmers_workbench_right/`, `config/items/farmers_workbench/`, `config/items/README.md` if it lists stations
- `config/recipes/farmers_workbench/`, `config/recipes/group_seeds/`, `config/recipes/melon/`, `config/recipes/README.md`
- `textures/blocks/farmers_workbench_*`, `textures/items/farmers_workbench`, `textures/vector.yaml` (`needs`)
- `run/src/world/bench/` (`index.c`, `README.md`)
- `run/include/ui/screen_inventory/index.h`, `run/src/ui/index.c`, `run/src/ui/README.md`
- `run/src/save/state/index.c`, its README
- `run/src/item/crafting/README.md`, `run/src/world/block_state/README.md` if it lists stations
- `tests/run/item/crafting/station/`, `tests/run/item/crafting/`, `tests/run/player/interaction/bench/`, `tests/run/ui/screens/`, `tests/cuda/shapes/` (row and golden), `tests/run/save/state/` if it has a grid test

## Steps

1. **`farming` in the tool** and `config/recipes/README.md`. *Why: the station must exist before any recipe names it.*
2. **Blocks, item, pictures.** Paint, write `index.mctex` with `texture_vector recipes`, run `texture_vector check`. *Why: "A new block" (`plans/REPO_STRUCTURE.md` section 13).*
3. **`BENCH_SHAPES`**, the screen kind, the block-to-screen rows. *Why: placing, breaking and opening must agree (`run/src/world/bench/README.md`).*
4. **The save check** accepts `farming`. Test: a grid saved on it reopens. *Why: the bench grid is saved with its station.*
5. **`group_seeds`, the bench's recipe, `melon` on.** *Why: the station is made and does something.*
6. **Tests**, below; the shapes golden made again with `MC_GOLDEN_WRITE=1`, looked at, described here. *Why: a station is several systems that must agree.*
7. **READMEs.** Tick each todo as it lands.

## Todo

From the master (section 9, phase 01):

- [ ] `farming` in `GR_STATIONS` (on the end); `RECIPE_STATION_FARMING`; the save check accepts it
- [ ] Blocks `farmers_workbench_left` and `_right`, item `farmers_workbench`, procedural pictures
- [ ] `BENCH_SHAPES` row; `UI_SCREEN_FARMING`; `ui_screen_kind_of_block`
- [ ] `group_seeds` and the `farmers_workbench` recipe at the table
- [ ] `melon` turned on at `farming`, as the station's first recipe
- [ ] Tests: station, placing and breaking, screen, shapes, save; READMEs

Expanded:

- [ ] Every array sized by `RECIPE_STATION_COUNT` or `UI_SCREEN_KIND_COUNT` found and given its farming entry (list them in Issues found)
- [ ] `texture_vector check` passes
- [ ] `standards_lint`, `clang_format`, the whole debug suite pass; `pgrep -af "$PWD/build"` prints nothing

## Tests

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `the_crafting_table_makes_only_the_other_stations_...` (existing, with the bench in `IS_TABLE_MAKES`) | The table's recipes | 6 stations, nothing else | |
| `the_farmers_workbench_is_made_from_three_seeds_over_two_logs` | Each seed kind, each wood | 1 bench out; 2 seeds or 1 log short gives nothing | Pattern or group wrong |
| `each_station_item_places_its_first_part_and_every_part_drops_one` (existing) | With the bench | 1 drop for any part mined | |
| `each_station_two_long_goes_down_across_the_view_as_the_bench_does` (existing) | With the bench, 4 facings | Both parts placed, same facing | Shape row wrong |
| `a_block_opens_only_its_own_screen` (existing) | Both parts | `UI_SCREEN_FARMING` | Row missing |
| `each_screen_has_the_slots_it_should_...` (existing) | Farming screen | The crafting screen's slots | |
| `nine_slices_make_a_melon_at_the_farmers_workbench_only` | Every station and the hands | 1 melon at `farming`; nothing elsewhere | `off` left, or wrong station |
| `a_bench_grid_saved_on_farming_reopens` | Save and reopen | Same grid, same station | Save check refuses 6 |
| `a_world_saved_before_this_opens` | A state file with a woodworking grid | Opens | Numbers moved |
| `tests/cuda/shapes` | The row of stations | Golden made again, looked at | |

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh cmake --build --preset debug --target mc_tool_texture_vector
    tools/test-or-build.sh build/debug/bin/tools/texture_vector check --textures textures --materials textures/vector.yaml
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^run_item_crafting.*$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_player_interaction.*|run_ui_screens|run_save_.*)$' --output-on-failure
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(render_feature_shapes|compile_game_tables)$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    pgrep -af "$PWD/build"    # must print nothing

`render_feature_shapes` is `tests/cuda/shapes/`, as plan 005 phase 06 ran it; `compile_game_tables` checks the stations in the tool.

## Gate

Ahmed collects seeds from grass, makes the farmer's workbench at a crafting table, places it, opens it and sees "Farmer's Workbench", and makes a melon from nine slices. He looks at the bench next to the woodworking bench and says whether it reads as its own thing.

## Risks and open questions

| Risk or question | Default |
|---|---|
| An array sized by a station or screen count is missed | The build's asserts and the existing tests catch most; each found is listed |
| The pictures read too like the woodworking bench | Greens and straw, the trough and the grain; Ahmed looks at the gate |
| Q5: the recipe | 3 seeds over 2 logs |
| Q6: the size | 2 by 1 |

## Commits

Appended as they land:

- (none yet)

## Issues found

None.
