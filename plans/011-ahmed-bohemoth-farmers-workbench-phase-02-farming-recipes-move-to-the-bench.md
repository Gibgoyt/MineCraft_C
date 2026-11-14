# 011-ahmed-bohemoth-farmers-workbench — phase 2: Farming recipes move to the bench

**Status:** not started   (updated 2026-11-14). Waits for Ahmed's go on plan 011, and for his answer to Q1.

Read first: `plans/011-ahmed-bohemoth-farmers-workbench-phase-00-vision-and-plan-files.md`, then the master `plans/011-ahmed-bohemoth-farmers-workbench.md` (sections 0 "The recipes as they stand", 2.5, 2.6, 5, 6 and 10), then phase 01's file.

## Goal

The recipes that are farming's work are made **only** at the farmer's workbench: `melon_seeds`, `pumpkin_seeds` and `bone_meal`, moved from the hands, as Ahmed answers Q1 (default yes), joining phase 01's `melon`.

**Everything else stays where it is:**
- food from crops (`bread`, `cookie`, `pumpkin_pie`, `mushroom_stew`), `sugar`, the dyes and planks in the hands;
- `paper` and `bowl` at woodworking;
- every hoe at its material's station.

**What says so:**
- `config/recipes/README.md`: the stations table, the hands' line, the unlock order and the departures table.
- `run_item_crafting_station`:
  - every recipe is reached from what the world gives, walking the stations in the new order;
  - nothing is made in two places;
  - no recipe of `farming` matches at `woodworking`, nor the other way.
- `run_playthrough` passes unchanged, since it crafts nothing that moves.

## Where this fits

**The vision.** Ahmed wants the farmer's workbench "separate ... entirely from the woodworking bench". Phase 01 made the station; this phase gives it the work that belongs there, and proves the two benches share no recipe.

**What this phase must NOT build:**
- No new item or recipe beyond the moves (no saplings, essences, watering, seed bags: master section 8).
- No move of food, sugar, dyes, paper, bowls or hoes unless Ahmed answers Q2 to Q4 otherwise, and then recorded here with his words.
- No change to a recipe's pattern, ingredients or count: only its `station`.

## Ahmed's words

- "Maybe also add plans to create a farmers workbench and separate that entirely from the woodworking bench??? add a phase in the plan for that too please!"
- "I think for farming bench put that at ./plans/011-* so that I know that we are doing that task later."

---

## Background, explained

### The hands' recipes today

`config/recipes/README.md`, the `hands` row (Ahmed's order, 2026-11-10): "Logs to planks, the crafting table from 4 logs or 4 planks, and what needs no tool: dyes, bone meal, sugar, seeds, simple food, clay, snow, coarse dirt."

Moving bone meal and seeds out changes that line. So **Q1 is asked before any file changes**: the main thread puts it to Ahmed, and his answer is written here first.

### Why these three and not more

| Recipe | Moves? | Reason |
|---|---|---|
| `melon_seeds`, `pumpkin_seeds` | Yes (Q1) | Getting seed from a crop is the farmer's work; a player who farms has the bench (it is made from seeds) |
| `bone_meal` | Yes (Q1) | It is what makes crops grow |
| `bread`, `cookie`, `pumpkin_pie`, `mushroom_stew` | No (Q2) | Eating must not wait for a bench |
| `sugar` | No (Q2) | A cooking ingredient |
| `paper` | No (Q3) | A craft good, beside books at the leather workbench |
| hoes | No (Q4) | Tools are made by their material: woodworking, stoneworking, smith table |

### What the playthrough crafts

`tests/run/playthrough/` makes planks, a crafting table, the woodworking bench, stairs and wooden tools (the day, bench and stairs scripts). None of those moves. This phase confirms it by a run, not by reading alone.

## Design

- In each moved recipe's `index.yaml`, `station: hands` becomes `station: farming`. The hands' grid is 2 by 2 and the bench's 3 by 3, so every moved pattern still fits.
- `config/recipes/README.md`:
  - **the stations table:** `hands` loses "bone meal, seeds"; a `farming` row gains "Seeds from melons and pumpkins, bone meal, a melon from nine slices";
  - **the field table's** station list includes `farming`;
  - **the unlock order** gains the master's 2.6 step;
  - **the departures table:** `melon` loses its `off` line in "What has no recipe, on purpose".
- `run_item_crafting_station`:
  - `IS_STATION_ITEM[RECIPE_STATION_FARMING]` is set (phase 01);
  - the order walk includes the bench after the woodworking bench, and seeds and bones among what the world gives (tall grass, skeletons, melons where they grow).

## Owned paths

This phase may edit these, plus this file:
- `config/recipes/melon_seeds/`, `config/recipes/pumpkin_seeds/`, `config/recipes/bone_meal/`, `config/recipes/README.md`
- `tests/run/item/crafting/station/`, `tests/run/item/crafting/`
- `tests/run/playthrough/` only if a run shows it needs a change (recorded)

## Steps

1. **Q1's answer** written into Issues found, with Ahmed's words. If no, this phase does only the README and the separation test, and says so. *Why: it changes his order for the hands.*
2. **Move the three recipes.** *Why: the farmer's work at the farmer's bench.*
3. **The README**, every table and the order. *Why: "every line of it is in these files, so moving a recipe is changing its `station`" (`config/recipes/README.md`); the README is how a person learns where things are made.*
4. **Tests:**
   - `the_farmers_workbench_and_the_woodworking_bench_share_no_recipe`: every recipe of one, laid in the other's grid, makes nothing;
   - `seeds_and_bone_meal_are_made_at_the_farmers_workbench_only`;
   - the order walk.
   *Why: "entirely separate" made checkable.*
5. **`run_playthrough`**, run and recorded. *Why: the day's crafting must still work.* Tick each todo as it lands.

## Todo

From the master (section 9, phase 02):

- [ ] Ahmed's answer to Q1 recorded; then `melon_seeds`, `pumpkin_seeds`, `bone_meal` move to `farming`
- [ ] `config/recipes/README.md`: stations table, hands' line, unlock order, departures table
- [ ] `run_item_crafting_station`: the order walk, the separation test, nothing in two places
- [ ] `run_playthrough` unaffected, checked

Expanded:

- [ ] Q2 to Q4 answers, if Ahmed gave any, recorded and applied; otherwise their defaults
- [ ] `compile_game_tables` passes (no clash, the hands' 2 by 2 rule)
- [ ] `standards_lint`, `clang_format`, the whole debug suite pass; `pgrep -af "$PWD/build"` prints nothing

## Tests

| Test | Checks | Numbers | Fails when |
|---|---|---|---|
| `seeds_and_bone_meal_are_made_at_the_farmers_workbench_only` | The hands and every station | 1 out at `farming`; 0 elsewhere | A station left as `hands` |
| `the_farmers_workbench_and_the_woodworking_bench_share_no_recipe` | Every recipe of each, in the other's grid | 0 matches | A pattern shared |
| `each_recipe_is_made_at_its_own_station_only_and_nothing_in_two_places` (existing) | All recipes | Holds | |
| `every_recipe_is_reached_from_what_the_world_gives_walking_the_stations_in_order` (existing, new order) | All recipes | Every recipe reached | The bench unreachable, or a seed source missing |
| `run_playthrough` (existing) | The day, bench, stairs, felling | Passes as before | The day crafts something moved |

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_item_crafting.*|compile_game_tables)$' --output-on-failure
    tools/test-or-build.sh timeout 1800 ctest --preset debug -R '^run_playthrough$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^(standards_lint|clang_format)$' --output-on-failure
    tools/test-or-build.sh timeout 3600 ctest --preset debug --output-on-failure
    pgrep -af "$PWD/build"    # must print nothing

## Gate

Ahmed tries to make bone meal from a bone in his inventory's grid and cannot. He makes it at the farmer's workbench, then seeds from a melon slice there. He checks the woodworking bench makes none of them.

## Risks and open questions

| Risk or question | Default |
|---|---|
| A new player wants melon seeds before having the bench | The bench is made from wheat seeds on day one; recorded at the gate if Ahmed finds it slow |
| Q1: move seeds and bone meal out of the hands? | Yes |
| Q2: food and sugar to the bench? | No |
| Q3: paper to the bench? | No |
| Q4: hoes to the bench? | No |

## Commits

Appended as they land:

- (none yet)

## Issues found

None.
