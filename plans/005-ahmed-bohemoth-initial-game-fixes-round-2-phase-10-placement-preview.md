# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 10: See a block before placing it

**Status:** built and agent-verified; left: Ahmed's gate   (updated 2026-11-10)

## Goal

Holding **any** block, you see a translucent blue copy of it exactly where it would go, in its real shape and facing, before you click. That includes the next block of a 1×1 bridge.

**Later words from Ahmed:** "any block needs blue opaque before placing, such as when creating a 1x1 bridge, I can easily see when I am placing a block in-front to extend the bridge!" Placing ahead while bridging is phase 13, and facing and turning are phase 12.

**Ahmed's words:** "similar to hypixel we need to be able to see blocks in like a translucent blue color before placing them!", then "placement failing shows red! I can also rotate it with some button I am not sure which!"

## How it is today

The game outlines the block you're aiming at, but shows nothing of where a held block would land, or what shape it would take: a wall torch, a slab, a two-voxel bench, a 2×2 smith table.

## Design

- **Where:** the exact cell placing would use, from the same code as placing in `run/src/player/interaction/`, so the preview can never disagree with the click. Each frame, a pure "where would this go, and as what block" function answers both.
- **What:** the real block that would be placed, in its real shape: the right wall torch for the face, both halves of a bench, all four cells of a smith table, and a door's two halves when doors exist.
- **How it looks:** translucent blue, about half see-through, over the world, drawn on the card by the same boxes used for blocks and entities (`cuda/src/entity_rays/`, `run/src/render/entities/`).
- **When placing would fail:** blocked, no ground for a torch, or a column not loaded. **It shows red,** as Ahmed decided.
- **Facing and turning:** the preview faces the way the block will be placed, which by default is the player's direction. R turns it a quarter turn each press. Both come from phase 12, which this needs first. **Proposed key: R,** which nothing in the game uses yet. It's Ahmed's to change.
- **Bridging:** when the aim hits nothing but phase 13's forward cell applies, the preview shows that cell, so you see the next bridge block first.
- **Setting:** on by default, with a box in Settings beside Coordinates.

## Todo

- [x] Split placing in `run/src/player/interaction/index.c` into "work out what and where" and "do it", and use the first for both the click and the preview
- [x] Send the preview, as the cells and their block ids, from the sim to the renderer each tick, as the outline is sent
- [x] Draw it on the card as translucent blue boxes, in each block's real shape, over the world and under the HUD
- [x] Draw the preview red when placing would fail, using the same check as placing
- [x] Add R to turn the preview a quarter turn, kept while holding the same item, and used when placing. Needs blocks with facings
- [x] Add a Settings box, saved in `settings.yaml`, on by default
- [x] Add tests: the preview cell equals the placed cell for every face, every shape, the bench and the smith table. Nothing is shown when placing would fail
- [x] Add golden pictures of a cube, a wall torch, a slab and a bench preview, and record the cost in ms

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_player_interaction.*|run_render_frame|render_feature_.*|run_settings|run_ui_screens|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: hold dirt, a torch and a bench in turn, aim around, and check the blue copy always lands where the click puts it.

## Commits

Appended as they land:

## Issues found

**Decided by Ahmed, 2026-11-06:** a failed placement shows red, and the preview can be rotated.

**Rotation needs a block state,** so a block can face a way. That's phase 2's open question. With it, benches, the smith table, doors, stairs and ladders can all turn.

**Open for Ahmed: which key rotates?** Default: R.

**Not verified against Hypixel.** Ahmed's description is the spec. He can say what to change after comparing.


### 2026-11-10, the build of this phase

**1. Splitting placing and R were phase 12's.** `player_interaction_place_plan` is the whole decision and `player_interaction_tick` places exactly what it says; R is `player_interaction_turn`. This phase added `player_place_preview` in `run/src/player/`, which builds the hands' input with the same function as `player_interact_fell_tick` (`player_hands_input`: the target, the body, the facing from the yaw main last sent) and asks the plan. So the preview and the click share every line but the edit. With a screen holding the buttons it shows nothing. The plan struct is untouched.

**2. From the sim to the card.** `run/src/game/tick/` asks `player_place_preview` after `player_tick`, as the outline's target is set, and sends every cell and voxel the plan gives with `render_entities_preview_set`, into a new `render_entities_preview_t` in each snapshot slot. The card side holds up to `ENTITY_RAYS_PREVIEW_CELLS` = 4, and a `static_assert` in `game_preview_publish` fails the build if `PLAYER_PLACE_CELLS` ever passes it. **Generalised:** the preview draws whatever cells the plan returns, 1 to 4, so phase 2's two door cells and phase 13's cell ahead need no work here; past 4 (a smith table's 4 fits) raise the constant. `render_entities_t` grew from 640 to 832 bytes, and holds 29 mobs where it held 30.

**3. Drawn in the overlay, not as entity boxes.** An entity box is opaque and shaded with the world, and half see-through would have meant changing shade. The outline's overlay already runs on the finished picture, after post and before the HUD, with the depth of each pixel. So the preview is a kernel of its own there (`entity_rays_preview_kernel`), started before the outline so the outline's lines lie over it, over the rectangle its cells can touch. Each cell is cut against the boxes the card draws its block in: the world's shapes table and `traverse_shape_rows`/`traverse_boxes`, turned by the voxel's facing with `block_facing_box_turn`, as `traverse_voxel_hit` does. It is hidden by the outline's rules (a nearer surface or box, not water). Where it shows, half of its colour, lit by the face it is on (top 256, x sides 205, y sides 174, bottom 140 of 256), goes over the picture: `entity_rays_preview_blend`. Blue `0x3C8CFF`, red `0xFF3030` when the plan is `BLOCKED`. It never touches TAA's history, so it does not ghost.

**4. Red is the plan's own check.** `BLOCKED` is what placing refuses: the cell taken, no ground for a torch or a sapling, a column not loaded, outside the heights, the body in the way, a bench's second cell taken. A torch aimed at an underside is `NONE` in the plan, so nothing shows there, not red: the plan says nothing would go anywhere. The todo's "Nothing is shown when placing would fail" was before Ahmed decided on red; the tests check red instead.

**5. The Settings box.** `place_preview` in `settings.yaml` (`settings_values_t` byte 34, on by default, a file without it on), "Placing preview" on the HUD page at row 5, under Coordinates, Date, the climate lines and phase 7's Pause when away. It reaches the card as `game_config_t.hide_place_preview` (the spare byte 35) and then `render_entities_preview_show`, a relaxed atomic byte main writes and the gpu thread reads. It is not a frame feature bit, because changing features stops and restarts the threads and refills. A reopened frame starts shown, so `game_open` and both paths of `game_reconfigure` say it again. `settings_hud.png` was redrawn for the new row and looked at.

**6. Tests.** `run_player_interaction_preview` (new, host only): ten blocks (cube, slab, stairs, torch, ladder, vine, sapling, lily pad, glass, bench) on all six faces, four look facings and 0 to 3 turns: an `OK` plan is exactly what the click puts down, both bench cells included, and anything else places nothing (848 OK, 80 of them two cells, 96 blocked, 16 nothing to show). Tall grass is placed in its own cell. A column not loaded, the body, a torch with no ground and a bench's second cell taken are `BLOCKED` with their cells. Through `player_place_preview`, looking 16 ways at four tilts with each block, what is shown after a tick is what the next click places. `render_feature_preview` (new, on the card): the blend on paper; every pixel changed is the bare picture under the colour on a face, red the same pixels; the setting off gives the bare picture to the byte; for a cube, wall torch, slab, bench and east-facing stairs, the preview covers exactly the pixels the block covers once placed (0 different for all five); behind a wall or under the floor it changes nothing; no heap in a frame; more than 4 cells refused. Added to `MC_ART_TESTS`. `run_settings` and `run_ui_screens` have the new setting and box.

**7. Golden pictures,** with every effect a player has, looked at: `tests/cuda/preview/fixtures/cube.png` (oak planks, so the silhouette test can see it on the stone floor), `wall_torch.png` (on the west face of a planks post), `slab.png`, `bench.png` (both halves) and `blocked.png` (the cube red).

**8. Cost: about 0.12 ms at 1080p** for a bench's preview two blocks ahead, the largest a preview gets but one at the eye, on the card's own clock (the overlay is counted in post). RTX 2080 Super, debug build, real terrain at render distance 10: post 1.218 ms with none, 1.346 with the bench, 1.242 with none again, 1.357 with the bench again. Frames 14.0 to 14.4 ms, within their noise. With nothing in hand nothing starts.

**9. Not verified in a live game here.** The sim-to-card path (`game_preview_publish`) is covered only by building and by the headless runs in the suite; Ahmed's gate is the live check.


**10. Verification, 2026-11-10.** `tools/test-or-build.sh cmake --build --preset debug`: exit 0. `tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_player_interaction.*|run_render_frame|render_feature_.*|run_settings|run_ui_screens|standards_lint|clang_format)$' --output-on-failure` (run with 1800 s, as the render_feature tests together take about 11 minutes here): 18 of 18 passed, `render_feature_entities` among them with its goldens unchanged. `pgrep -a minecraft_c` and `pgrep` of this worktree's build: nothing.
