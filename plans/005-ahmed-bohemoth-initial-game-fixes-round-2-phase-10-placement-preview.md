# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 10: See a block before placing it

**Status:** not started   (updated 2026-11-05)

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

- [ ] Split placing in `run/src/player/interaction/index.c` into "work out what and where" and "do it", and use the first for both the click and the preview
- [ ] Send the preview, as the cells and their block ids, from the sim to the renderer each tick, as the outline is sent
- [ ] Draw it on the card as translucent blue boxes, in each block's real shape, over the world and under the HUD
- [ ] Draw the preview red when placing would fail, using the same check as placing
- [ ] Add R to turn the preview a quarter turn, kept while holding the same item, and used when placing. Needs blocks with facings
- [ ] Add a Settings box, saved in `settings.yaml`, on by default
- [ ] Add tests: the preview cell equals the placed cell for every face, every shape, the bench and the smith table. Nothing is shown when placing would fail
- [ ] Add golden pictures of a cube, a wall torch, a slab and a bench preview, and record the cost in ms

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
