# 001-ahmed-bohemoth-minecraft-from-scratch — phase 13, stage 1: Items and inventory

**Status:** done   (updated 2026-11-02)

## Goal

Item tables, stacks, hotbar and a 36-slot inventory exist.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-13-items-survival.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/item/index.*` and `README.md` (the Owner tier)
- `config/items/*`
- `run/{src,include}/item/stack/`
- `run/{src,include}/item/inventory/`
- `tests/run/item/inventory/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Create the `item` Owner tier: `run/src/item/index.c` with paired `item_init` and `item_deinit`, owning the regions and arenas its parts borrow
- [x] Define items in `config/items/<name>/index.yaml`, and generate tables through `compile/src/game_tables`
- [x] Implement stacks in `run/src/item/stack/index.c`
- [x] Implement the hotbar and 36-slot inventory in `run/src/item/inventory/index.c`
- [x] Register test `run_item_inventory`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_item_inventory$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `c76a31b` Add item tables from config/items, stacks, and a 36-slot inventory with a hotbar that the player picks up into and places from.

## Issues found

1. **`run_types` counts the program's regions.** Expected it to pass unchanged; it failed at `tests/run/types/index.c:914` and `:922`, wanting `heap at exit: 10 reserved, 10 released` and `leak at exit: 10 regions`, with 11 now that `item_t` holds one. Both numbers were changed to 11, which is what they are there to notice.
2. **A drop must now name an item file.** Stage 07.1 let a drop exist by a block's name or an item's texture, until this stage. `gb_item_exists` now wants `config/items/<item>/index.yaml`, so the small trees in `tests/compile/game_tables/` gained items of coal and stone, and its two `block_drop_items` expectations gained `.item`. Three tests there changed for it, and new ones hold the rule.
3. **Wiring outside the owned paths,** all of it the smallest found: `compile/{src,include}/game_tables/items/` and its call in `compile/src/game_tables/index.c` (the todo asks for it); `compile/src/game_tables/blocks/` for `block_drop_items[i].item`; `cmake/generated.cmake`; `tests/compile/game_tables/`; `config/blocks/README.md`; `run/src/game/` and `run/include/game/` (`game_item_set`, `game_hotbar_send`, and the hand set from the hotbar around each tick); `run/src/main/`, `run/include/main/` (`item_t item`, 192 bytes more of `app_t`); `player_interaction_hand_set` in `run/{src,include}/player/interaction/`; `plans/REPO_STRUCTURE.md`.
4. **`player_interaction_hand_take` stays, with a narrower job.** Its README said this stage replaces it. In `--walk` nothing calls it now, but `run_player_interaction`, `run_entity_drop` and any `game_t` with no `game_item_set` still do, and those tests belong to their stages, so it was kept as the path with no inventory, and `game_t` falls back to it. Left for whoever wants one path only.
5. **Not reachable by any test here: the real keyboard and wheel path.** `game_loop_source` reads `frame->pressed` and `frame->wheel`, which needs KMS mode and a real input device, neither of which may be opened here. The number keys are proved through the script path in the built program, and scrolling through `item_hotbar_scroll_send` and `item_hotbar_apply`, with `game_hotbar_send`'s wheel argument only ever given 0. The script format has no wheel. Left for Ahmed's own run.
6. **Items that do less than their name, by what exists so far.** `water_bucket` and `lava_bucket` place nothing, as placing one must also leave an empty bucket; doors, `bed`, `oak_sign`, `melon_seeds` and `pumpkin_seeds` place nothing, as those blocks do not exist. No HUD shows the hotbar until Stage 14.2, so for now only the log line at the end of a run says what is in hand. Durability, food and armor are read into the table and used by nothing until Stages 13.3 and 13.4.

## Gate check, 2026-11-03

Nothing false found here. `item_stack_drop_row`'s comment no longer says wear is lost. Issue 5 stands: the real keyboard and wheel can only be checked by Ahmed.
