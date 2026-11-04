# 003-ahmed-bohemoth-initial-game-fixes — phase 2: Wall torches

**Status:** not started   (updated 2026-11-04)

## Goal

Aiming a torch at the side of a block puts it on that wall, leaning out, lit and drawn correctly. It drops a torch if it's broken or if the block holding it goes.

**Ahmed's words:** "torches can not be put onto a wall let's please make that possible!"

## Why it happens

- `interaction_place` in `run/src/player/interaction/index.c:304` always places the same block, whichever face you aim at.
- `interaction_needs_ground` (same file, line 199) requires solid ground below any `BLOCK_SHAPE_TORCH`, so aiming at a wall with nothing below fails.
- A voxel holds a block id and nothing else, so no block has a facing (Stage 09.1 issue 1). There's one torch box, standing in the middle (`cuda/include/traverse/index.hu:73`).

## Approach

Follow the existing one-id-per-variant model, with no new block state: four new blocks, one per wall. The `blocks/torch_wall` texture already exists and is unused.

## Todo

- [ ] Add `config/blocks/torch_wall_{east,west,north,south}/index.yaml`: light 14, drops `torch`, texture `blocks/torch_wall`
- [ ] Add four leaning boxes to the table in `cuda/include/traverse/index.hu`, the `TRAVERSE_SHAPE_*` list in `run/include/gpu_bridge/traverse/index.h`, and the shape names in `compile/src/game_tables/blocks/index.c`
- [ ] Add the matching `BLOCK_SHAPE_*` cases to collision and targeting in `run/src/physics/collision/` and `run/src/physics/raycast/`
- [ ] In `interaction_place`, choose the block from the face hit: a side gives the matching wall torch, a top gives today's floor torch, and a bottom places nothing
- [ ] A wall torch needs the solid block behind it. When that block is mined, or destroyed by an explosion, the torch breaks and drops, as `interaction_stack_break` does for what stands on top
- [ ] Handle the new shapes wherever `BLOCK_SHAPE_TORCH` is checked: `tick_is_open` in `run/src/world/tick/index.c:95`, `run/src/world/light/`, `run/src/entity/explosion/`, `run/src/mob/spawn/`, `run/src/render/lod/`, `run/src/render/frame/`
- [ ] Add tests in `tests/run/player/interaction/`: each of the four faces gives the right block, a top face gives a floor torch, a bottom face gives nothing, and breaking the block behind drops the torch
- [ ] Add a scripted headless screenshot of all four, and check it by eye

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_player_interaction|run_world_light|run_world_tick|run_physics.*|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: craft torches, place them on all four sides of a block and on the floor, then mine the block behind one.

    ./build/release/bin/minecraft_c --windowed

## Commits

Appended as they land:

## Issues found

None.
