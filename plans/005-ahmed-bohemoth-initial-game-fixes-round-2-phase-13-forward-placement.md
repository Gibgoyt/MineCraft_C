# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 13: Placing ahead, for bridging

**Status:** not started   (updated 2026-11-06)

## Goal

Walking on a one-block-wide bridge, you can keep placing the next block in front of you while looking ahead, without turning round and crouching at the edge. The blue preview (phase 10) shows exactly where it'll go before you click.

**Ahmed's words:** "any block needs blue opaque before placing, such as when creating a 1x1 bridge, I can easily see when I am placing a block in-front to extend the bridge! ... we need to place a block without turning around and going to the edge of the block! in minecraft Bedrock I can place a block while facing forward! this is something we need to be able to do please!"

## How it is today

Placing needs the aim ray to hit a block face (`interaction_place` in `run/src/player/interaction/index.c`, with `raycast_hit_in_front`). On a bridge, looking ahead, the ray goes past the bridge's end into the air and hits nothing, so nothing can be placed. You have to turn round and aim back at the edge of the block you're on, as in Java Minecraft.

## Design

**Ahmed's description of Bedrock is the spec here. Bedrock's exact rules weren't looked up.**

When the aim hits nothing in reach, and you're looking ahead or down, the game offers a **forward cell**:

- **Which cell:** the one next to the block under your feet, in the direction you face (the nearest of the four), at the same height as that block.
- **Only if:** it's empty or replaceable (air, water, tall grass), it touches the block under you, it's within reach, and your body doesn't overlap it.
- **Looking up past a set angle** turns it off, so it never puts blocks in the air you didn't mean.
- **Sneaking** keeps you on the edge as now, so bridging stays safe.
- **The aim always wins:** if the ray hits a block, placing works exactly as now, so nothing changes for ordinary building.
- The preview (phase 10) shows the forward cell blue, or red when it can't be used, so you always see it first.

The same "where would it go" function serves the click and the preview, so they can't disagree.

## Todo

- [ ] Add the forward cell to the placement-target function from phase 10, used only when the aim hits nothing in reach
- [ ] Add the conditions above: empty or replaceable, touching the block underfoot, in reach, not in the body, not looking up past the angle
- [ ] Work with facing (phase 12): a stair placed ahead faces the player, as any other
- [ ] Add tests: walking a bridge placing ahead every few steps builds a straight line with no gap; looking up places nothing; with a block in the aim, it's used instead; never inside the body; never with nothing under the player
- [ ] Add a scripted headless proof: walking and bridging 20 blocks out over water, with screenshots of the blue preview at the end, checked by eye

## Verification

    tools/test-or-build.sh cmake --build --preset debug
    tools/test-or-build.sh timeout 900 ctest --preset debug -R '^(run_player_interaction.*|run_player_movement|run_playthrough|standards_lint|clang_format)$' --output-on-failure
    pgrep -a minecraft_c    # must print nothing

Ahmed: build a bridge over water, walking forward and placing blocks while looking ahead, and check the blue preview shows each one first.

## Commits

Appended as they land:

## Issues found

**Needs phase 10's preview,** and its shared placement function. Build them together, or 10 first.

**Open for Ahmed:**

1. How far down must you look for the forward cell to show? Default: anything from level to straight down, nothing above level.
2. Should it also build upward, placing under yourself when jumping, as Minecraft's pillaring does? Default: no, that works already by aiming down.
