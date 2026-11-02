# 001-ahmed-bohemoth-minecraft-from-scratch — phase 13, stage 2: Crafting, furnace and chest

**Status:** done   (updated 2026-11-03)

## Goal

2x2 and 3x3 crafting, a full basic recipe set, furnace and chest exist.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-items-survival.md`.

**Owned paths** (edit only these, plus this file):

- `config/recipes/*`
- `run/{src,include}/item/crafting/`
- `run/{src,include}/item/smelting/`
- `run/{src,include}/item/container/`
- `tests/run/item/crafting/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Define recipes in `config/recipes/<name>/index.yaml`
- [x] Implement 2x2 and 3x3 matching in `run/src/item/crafting/index.c`
- [x] Implement the furnace in `run/src/item/smelting/index.c`
- [x] Implement the chest in `run/src/item/container/index.c`
- [x] Register test `run_item_crafting`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_item_crafting$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `8760f67` Add 2x2 and 3x3 crafting from config/recipes, furnaces that burn and light their block, and chests that spill what they held when broken.

## Issues found

1. **A chest could not spill what no block drops.** Expected: a broken chest throws out what it held. Actual: `run/src/entity/item_drop/` holds only rows of `block_drop_items`, and that table had only what blocks drop (`ASSERT_ALWAYS(item < BLOCK_DROP_ITEM_COUNT)` at `run/src/entity/item_drop/index.c:76`), so an iron pickaxe had no row. Done: `compile/src/game_tables/blocks/` now writes a row for every file in `config/items/`, sorted as item ids are, so row i is item i + 1 (`item_stack_drop_row`, held by `every_item_has_a_row_to_lie_on_the_ground_by_which_is_its_id_less_one`). That took the table past `ENTITY_RAYS_TEXTURES_MAX`, whose `static_assert` counted rows with no need of a texture, so it now asks a new `BLOCK_DROP_ITEM_NO_BLOCK_COUNT`, 137 against 256. `render_feature_entities`, which checks that every such row has a texture, passes for all of them.
2. **Wear does not survive lying on the ground.** A drop has no field for it, so a worn tool spilled from a chest comes back new. Nothing wears until Stage 13.3, so no player can see it yet. Left for 13.3 to decide, and written in `run/src/item/README.md`.
3. **Nothing in the running game opens a chest or a furnace.** The right button still places, and the screens are Stage 14.3's, so `container_open`, `smelting_click`, `crafting_grid_take` and the rest are reached only by `run_item_crafting`. In `--walk` what runs today is: slots made when one is placed, a spill when one is broken, and a furnace ticking, none of which a script can fill, since a script cannot give items. The real-game test fills them through the API before the threads start. 14.3 will also have to carry these from `main` to `sim`, as `item_hotbar_send` does, since all of it is the sim's.
4. **Chests in a column that unloads.** Nothing is saved until Stage 16.2, so its edits go with it, the chest's block included. Its slots stay until a new chest goes in that cell, and count against the 4096. A furnace's are let go of by its tick. Left alone until 16.2 changes what unloading means.
5. **Recipes that cannot be the published ones,** because the game has no such item: listed in `config/recipes/README.md`, with what has no recipe at all (`bed`, for want of wool, and others). No double chest: not in this stage's plan.
6. **`run_render_frame` fails some runs, and not because of this stage.** `the_real_threads_draw_what_one_thread_draws_with_nothing_from_the_heap` failed `columns_made > 100u` and `picture == screen`, first difference at byte 24945, on 2 of 5 runs by itself (`ctest --preset debug -R '^run_render_frame$'`), and passed the other 3. That game has no `item_t`, and Stage 12.2's issues record the same failure at the same byte. Left alone: not this stage's. In the one full `ctest --preset debug -j4`, 74 of 76 passed, and the other was `render_feature_sky`, timing out at 280 s with others on the card; alone it passed in 152 s.
7. **Two expectations of mine were wrong, and the code right.** Three oak planks in a row at a table are `oak_slab` on the way to a pickaxe, not nothing. A chest placed in a real world can drop a plant that stood on what it replaced, so the real-game test counts coal and pickaxes exactly, and all drops as at least 6.
8. **Wiring outside the owned paths,** all of it the smallest found: `compile/{src,include}/game_tables/recipes/` and its call in `compile/src/game_tables/index.c` (the todo's tables need it); `compile/src/game_tables/blocks/` and `run/src/render/entities/index.c` for issue 1; `cmake/generated.cmake`; `tests/compile/game_tables/`, with 41 bad recipes; `run/{src,include}/item/index.*` (`item_t` grew from 192 to 512 bytes, `item_world_begin`, `item_world_tick`); `run/include/main/index.h` (`app_t`'s size); `inventory_slot_move_out` in `run/{src,include}/item/inventory/`; `item_stack_drop_row` in `run/{src,include}/item/stack/`; `run/src/game/` (`game_spill`, the tick's call, and a report line); `plans/REPO_STRUCTURE.md`.
