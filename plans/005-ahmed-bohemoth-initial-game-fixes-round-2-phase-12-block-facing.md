# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 12: Blocks that face a way

**Status:** not started   (updated 2026-11-06)

## Goal

Stairs, the woodworking bench, and every block with a front face the way you're looking when you place them, and you can turn them before placing.

**Ahmed's words:** "the issue with rotating blocks should be for stairs! stairs, as well as the wood working table, currently only place due north or something weird! we need to properly rotate it please! default facing in the direction the user is facing and the user can rotate before they place it!"

## Why it happens

**A voxel holds a block id and nothing else,** so no block can remember which way it faces. Plan 001's Stage 09.1 issue 1 recorded the workarounds:

- **Stairs** always rise towards `+y`, which is north here, so "due north" is right.
- **The woodworking bench** always lies along `x`, with its west half where you click (plan 003 phase 4).
- **Ladders and vines** always sit on the `+y` side.
- **Wall torches** got four separate blocks to fake it (plan 003 phase 2).

Doors (phase 2), the smith table (phase 6) and rotating the preview (phase 10) all need the same thing.

## Approach: a block state beside the id

**Recommended,** and the answer to phase 2's open question:

- Each voxel gets a small **state** next to its id. It says which way the block faces (4 ways), plus a few bits a block can use: a door's open or shut, and upper or lower half.
- A block's YAML says which states it has: `facing: horizontal`, `half: upper_lower`, `open: true`. Blocks without one ignore the state, so nothing else changes.
- **Everything that reads blocks has to read it:** sections, saves, encoding for the card, ray traversal and shapes, collision, light, the physics raycast, and drops. Drops ignore the state, so a stair drops one stair whichever way it faced.

**Why not more blocks instead** (option A): stairs alone would need 4 blocks per wood (32), the bench 8, and a door 16 per kind. Every one is another YAML file and another id, and the table gets hard to read. A state does it once for all of them.

## How placing faces

- **By default, a block faces the player,** using the nearest of the four directions to where they're looking. Stairs' steps rise away from you, as in Minecraft. A bench lies across your view. A door opens towards you.
- **R turns it a quarter turn** before placing, shown in the preview (phase 10). The turn is kept until you change what you're holding.
- **Upside-down stairs,** by clicking a block's upper half or its underside: Minecraft does this, and it isn't asked for here. Left for later.

## Todo

- [ ] Add a state to each voxel in `run/src/world/section/`, sized to fit in what a section stores, with its layout and padding written out per the coding standard
- [ ] Add `facing`, `half` and `open` fields to block YAML, read by `compile/src/game_tables/blocks/`
- [ ] Carry the state through saves (`run/src/save/world/`, with a version bump that keeps phase 1's name maps), GPU encoding (`run/src/render/encode/`, `cuda/src/world_buffers/`), traversal and shapes (`cuda/src/traverse/`), collision and raycast (`run/src/physics/`), light and ticks
- [ ] Turn shapes in the box table by facing, so stairs, ladders and vines turn
- [ ] Place facing the player: stairs, ladders, the woodworking bench and the stoneworking bench
- [ ] Turn the woodworking bench's two halves with its facing: along `x` or along `y`
- [ ] Replace the four wall torch blocks with one wall torch and a facing, translating old saves through phase 1's name map
- [ ] Add R to turn the held block, with phase 10 showing it
- [ ] Add tests: stairs facing each way look and collide right; a bench turns both ways; a saved world keeps every facing; old worlds open with the old blocks facing as before; terrain digests unchanged where nothing faces
- [ ] Add golden pictures of stairs and a bench in all four facings

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 1500 ctest --preset debug --output-on-failure
    tools/test-or-build.sh cmake --build --preset release
    pgrep -a minecraft_c    # must print nothing

The whole suite, as this touches nearly everything. Then a release build, which checks main's 16 KiB stack frame.

Ahmed: place stairs and a woodworking bench facing each way, and turn them with R before placing.

## Commits

Appended as they land:

## Issues found

**The biggest phase in plan 005.** It touches storage, saves, the renderer and physics. It goes before doors (phase 2), the smith table (phase 6) and rotation in the preview (phase 10).

**Speed:** a state per voxel adds memory and work for the card. Measure with `bench_world` before and after, and record it here.

**Open for Ahmed:** this is phase 2's question, answered by "we need to properly rotate it". Confirm a block state over more blocks? Default: a block state.
