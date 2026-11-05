# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 10: See a block before placing it

**Status:** not started   (updated 2026-11-05)

## Goal

Holding a block, you see a translucent blue copy of it exactly where it would go, in its real shape, before you click.

**Ahmed's words:** "similar to hypixel we need to be able to see blocks in like a translucent blue color before placing them!"

## How it is today

The game outlines the block you're aiming at, but shows nothing of where a held block would land, or what shape it would take: a wall torch, a slab, a two-voxel bench, a 2×2 smith table.

## Design

- **Where:** the exact cell placing would use, from the same code as placing in `run/src/player/interaction/`, so the preview can never disagree with the click. Each frame, a pure "where would this go, and as what block" function answers both.
- **What:** the real block that would be placed, in its real shape: the right wall torch for the face, both halves of a bench, all four cells of a smith table, and a door's two halves when doors exist.
- **How it looks:** translucent blue, about half see-through, over the world, drawn on the card by the same boxes used for blocks and entities (`cuda/src/entity_rays/`, `run/src/render/entities/`).
- **When placing would fail:** blocked, no ground for a torch, or a column not loaded. Show nothing, or red if Ahmed prefers (see Issues).
- **Setting:** on by default, with a box in Settings beside Coordinates.

## Todo

- [ ] Split placing in `run/src/player/interaction/index.c` into "work out what and where" and "do it", and use the first for both the click and the preview
- [ ] Send the preview, as the cells and their block ids, from the sim to the renderer each tick, as the outline is sent
- [ ] Draw it on the card as translucent blue boxes, in each block's real shape, over the world and under the HUD
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

**Open for Ahmed: when placing would fail, show nothing or show it red?** Default: show nothing.

**Not verified against Hypixel.** Ahmed's description is the spec. He can say what to change after comparing.
