# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 2: Doors

**Status:** not started   (updated 2026-11-05)

## Goal

Doors can be placed, are two blocks tall, face the way you placed them, open and close with the right button, and block you when closed.

**Ahmed's words:** "doors can not be placed down for some reason??? let's please fix this!"

## Why it happens

**Door blocks were never added.** It's the same as copper armour was:

- `config/items/wood_door`, `iron_door` and `glass_door` have only a name and a picture, and no `block:` field.
- `config/blocks/` has no door.
- So a door in your hand counts as an item that places nothing (`run/src/game/tick/index.c:40`, `ITEM_NO_BLOCK`).

## The real problem: blocks can't face a way or open

A voxel holds only a block id. That's now the fourth time this has come up: wall torches got four blocks, stairs face one way, ladders face one way, and the bench can't turn. A door needs **2 halves × 4 facings × open or shut = 16 variants for each kind of door**.

**Option A, as we've done so far:** 16 blocks per door kind, 48 for three kinds. No new system. Each variant is a YAML file and a box shape.

**Option B, a block state:** voxels also hold a small state (facing, open, upper or lower), stored next to the id. Doors need it, and it would let stairs, ladders and benches turn too. It's a bigger change: section storage, saves, GPU encoding and traversal, light and collision all read it.

**Recommended: B, as its own phase before doors.** A is quicker now but leaves stairs, ladders and benches stuck facing one way, and makes the block table much bigger. **This is Ahmed's call** (see Issues).

## Todo

With option A, as the default until Ahmed chooses:

- [ ] Add blocks for `wood_door`, `iron_door` and `glass_door`: each half, each facing, open and shut, as thin plates on the right side of the voxel
- [ ] Give each door item its block, choosing the facing from the player's direction and the hinge side from the neighbours
- [ ] Place both halves together, refusing if either cell is taken. Breaking either half removes both and drops one door, like the bench
- [ ] Open and close with the right button, swapping both halves at once. Iron doors stay shut by hand, as there's no redstone; say so in their README
- [ ] Collision: closed doors block, open ones don't. Mobs' pathfinding treats closed doors as walls
- [ ] Add textures: upper and lower for each kind, from the pack or our recipes
- [ ] Add tests in `tests/run/player/interaction/`: placing in each facing, both halves, opening, breaking either half, saving and reloading an open door
- [ ] Add a scripted headless proof with screenshots, checked by eye

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 600 ctest --preset debug -R '^(run_player_interaction.*|run_physics.*|run_mob_path|run_save_world|run_item_crafting|compile_game_tables|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: craft a wooden door, place it facing each way, open and close it, walk through, then break the top half.

## Commits

Appended as they land:

## Issues found

**Open for Ahmed: option A (16 blocks per door) or option B (a real block state, then doors)?** B is my recommendation, because it also fixes stairs and ladders facing one way, and lets benches turn.

**2026-11-06: B is now phase 12.** Ahmed asked for stairs and the woodworking bench to face the player and turn before placing, which needs a block state. So doors build on phase 12, and option A isn't needed unless Ahmed says otherwise.
