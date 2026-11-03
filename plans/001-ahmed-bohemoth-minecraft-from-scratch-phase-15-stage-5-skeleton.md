# 001-ahmed-bohemoth-minecraft-from-scratch — phase 15, stage 5: Skeleton

**Status:** done   (updated 2026-11-03)

## Goal

Skeleton keeps distance, fires arrows as projectiles, and drops items.

**Wave:** W8. **Phase:** `001-ahmed-bohemoth-minecraft-from-scratch-phase-15-mobs.md`.

**Owned paths** (edit only these, plus this file):

- `run/{src,include}/mob/skeleton/`
- `run/{src,include}/entity/projectile/`
- `tests/run/mob/skeleton/`

Read `plans/README.md`, `plans/CODING_STANDARD.md` and `plans/REPO_STRUCTURE.md` first. No AI trailers in commits. Never push. Never leave a process running.

## Todo

- [x] Implement keep-distance behaviour and drops in `run/src/mob/skeleton/index.c`
- [x] Implement arrows in `run/src/entity/projectile/index.c`
- [x] Register test `run_mob_skeleton`
- [x] Add a `README.md` (purpose, tier, what it owns, public functions, what may call it) to every new source directory, with exactly one `index.*` in each

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^run_mob_skeleton$' --output-on-failure
    tools/test-or-build.sh timeout 300 ctest --preset debug -R '^standards_lint$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

## Commits

Appended as they land:

- `b243fe8` Add the skeleton, which keeps its distance, shoots arrows that fly as projectiles and hit the player, and leaves its drops, and correct two struct sizes that stopped the merged tree building.

## Wiring outside the owned paths

- `run/include/entity/index.h`, `run/src/entity/index.c`, `run/src/entity/README.md`: `entity_t` holds `projectile_store_t`, started and ended with the world. `entity_t` grew from 576 to 704 bytes.
- `run/include/mob/index.h`, `run/src/mob/index.c`, `run/src/mob/README.md`: `mob_t` holds `mob_skeleton_t` in what was its padding, so its size is unchanged. It is started in `mob_open`.
- `run/src/game/index.c`, `run/src/game/README.md`: `mob_skeleton_seed` at open; `mob_skeleton_tick` before `entity_tick`; `projectile_tick` after it, with the player's box; `player_survival_hurt` and knockback on a hit; each arrow added to what is drawn.
- `tests/run/mob/path/index.c`, `tests/run/entity/item_drop/index.c`, `tests/run/entity/store/index.c`: each bounds its Owner's arena by a sum of parts, and the sum has a new part.
- `run/include/main/index.h`, `tests/run/ui/hud/index.c`: see issue 1.

## Issues found

1. **The integration branch did not build.** Expected: `cmake --build --preset debug` passes at 691c90d. Actual: `run/include/main/index.h:52: static assertion failed` on `sizeof(app_t) == 14080`, and with that past, `tests/run/ui/hud/index.c:118` on `sizeof(fixture_t) == 896`. Merges had grown the structs and not these numbers. Measured with a compiler diagnostic on `sizeof`: `fixture_t` is 1216, `app_t` is 14592 with this stage's 128 more bytes of `entity_t`. Fixed both numbers, since this stage's Verification needs the build. Other stages may do the same to the `app_t` line, and the merge then has to take the size of the merged struct.
2. **`run_types` fails, and it is not from this stage.** `tests/run/types/index.c:914` and `:922` expect `heap at exit: 12 reserved, 12 released` and `leak at exit: 12 regions`; the program prints 13. This stage reserves no region. Left alone, as it is another stage's test and not in this Verification: the number needs changing to 13 once the regions are settled.
3. **`run_render_frame` fails, in `the_real_threads_draw_what_one_thread_draws_with_nothing_from_the_heap`.** `tests/run/render/frame/index.c:1349`: `game->columns_made > 100u` is false, and `:1355`: `picture == screen: first difference at byte 24945`. Failed twice, once alone. That test sets no player, entity or mob on its game, so nothing this stage added is on its path. That is from reading the path, not from a comparison: 691c90d does not build, so there is no run without this stage to compare with. Left alone and reported.
4. **`render_feature_sky` timed out** at 280 s in a full suite with `-j 4`, and passed alone in 172 s, on a machine shared with other agents. No change made.
5. **`clang_format` fails on two lines not from this stage:** `run/include/game/index.h:122:25` and `run/include/main/index.h:32:13`, comment alignment left by merges. Left alone, so as not to add conflicts with stages merging next to them. Everything this stage wrote is formatted. `clang_format` is not in this Verification.
6. **A defect of this stage, found by its test and fixed.** Expected: a skeleton never steps into a fluid. Actual: `it_will_not_back_into_lava_or_water` failed on 357 checks. The look ahead tested only the middle of its box, so circling nearly along an edge put its side over the fluid. It now tests the cells under all four corners of the box.
7. **Nothing draws a mob's body yet,** as Stage 15.1's `run/src/entity/animation/README.md` says. So a skeleton shoots from where nothing is seen. Its arrows are drawn, as the arrow item's box, not turned along their flight, since `render_entities_add_item_drop` is what exists; `textures/mobs/arrow` is not used. Left for whoever draws bodies. Ahmed should know this before a night's test.
8. **An arrow can take another kind of mob to no health, and nothing removes it yet.** `run/src/mob/skeleton/` takes away only its own kind. The other kinds stand at no health, with arrows passing through them, until Stage 15.4, 15.6 or 15.7 reaps them. A skeleton is not angered by being hit, either.
9. **What the game wiring does on a real card is tested once, not looked at.** `in_a_real_game_a_skeleton_shoots_the_player_and_leaves_its_drops` runs `game_t`'s tick headless, on compute only, and checks the books and the snapshot of boxes. Nobody has seen an arrow on a screen.
