# 005-ahmed-bohemoth-initial-game-fixes-round-2 — phase 13: Placing ahead, for bridging

**Status:** done, Ahmed's live check left   (updated 2026-11-10)

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

- [x] Add the forward cell to the placement-target function from phase 10, used only when the aim hits nothing in reach
- [x] Add the conditions above: empty or replaceable, touching the block underfoot, in reach, not in the body, not looking up past the angle
- [x] Work with facing (phase 12): a stair placed ahead faces the player, as any other
- [x] Add tests: walking a bridge placing ahead every few steps builds a straight line with no gap; looking up places nothing; with a block in the aim, it's used instead; never inside the body; never with nothing under the player
- [x] Add a scripted headless proof: walking and bridging 20 blocks out over water, with screenshots of the blue preview at the end, checked by eye

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


### 2026-11-10, the build of this phase

**1. Defaults taken.** The cell ahead shows when looking anywhere from level to straight down (pitch <= 0), never above level. No pillaring change: it already works by aiming down.

**2. Inside the plan, in one helper.** `interaction_ahead_hit` in `run/src/player/interaction/index.c` (with `interaction_ahead_support` and `interaction_ahead_in_reach`) is the whole of it. When the aim is `RAYCAST_RESULT_MISS` (a column not loaded is not a miss) and the input's new `look_ahead` byte is set, it finds the block underfoot and hands `player_interaction_place_plan` the hit the aim would have made on that block's side. Everything after (voxel, facing, cell check, body check, a bench's other half) is the plan's own, unchanged. The only other change to the plan is `hit` becoming a pointer that may point at that synthetic hit, so phase 2's door code merges beside it. `player_interaction_tick` used to return on a miss; it now places through the same plan on a miss with the right button, so the click and the blue/red preview cannot disagree.

**3. The input.** `player_interaction_input_t.look_ahead` (byte 27, out of what was padding; the struct is still 40 bytes) is set by `player_hands_input` in `run/src/player/` from the pitch main last sent, the same pitch the aim uses. Every other caller (tests, anything building the input by hand) leaves it 0 and gets exactly the old behaviour.

**4. The block underfoot** is the solid block (`BLOCK_FLAG_SOLID`) under the body's footprint, 0.001 below the soles, nearest the middle of the feet. A body sneaking out past an edge still stands on the block it hangs from. In a jump, in water, or over air there is none, and nothing is offered.

**5. Nothing rather than red, except for the body.** If the cell ahead is not replaceable, not loaded, or out of reach, the plan says `NONE`. If it were red instead, walking on ordinary ground looking at the horizon would show a red block in front of you all the time. Red (`BLOCKED`) is kept for a cell that could take a block but the plan's own checks refuse it: the body in the way (as at the edge of a slab, where the cell ahead reaches up into the body), or a torch or sapling with no ground under it.

**6. Facing (phase 12).** Since what goes ahead is placed as if aimed at the side of the block underfoot, stairs face the way the player looks, turned by R, like any other stairs; a ladder lies on that block's side; a torch hangs from it as a wall torch. The test checks the plan ahead is byte for byte the plan aimed at that side with the same look and turns.

**7. Sneaking** is movement's, as before: it holds the body on the edge, and placing ahead still works from there (the bridge test sneaks too).

**8. Tests.** `run_player_interaction_forward` (new, host only, `tests/run/player/interaction/forward/`): through a real `player_t` on a one-block platform in a lake, walking east at pitch -0.5 and clicking every 3 or every 1 ticks, then sneaking and clicking every 2 or every 7: each time all 20 planks go down from x = 1 to 20 at the platform's height, with no gap and nothing else in the lake. The feet never drop below the bridge's top (lowest 0.000). Before every click the preview was `OK` on exactly the cell filled. Looking up: 31 pitches from -1.5 to +1.5; all 15 above level place nothing; the 12 from level down where the aim misses place ahead. The aim wins: 24 aims (every face, every look) give the same plan bytes with `look_ahead` on or off, and place where aimed. Never in the body: cube and slab, standing and crouched, 40 positions out to the edge, 4 looks: 408 placed, 232 red with the body in the way, and an `OK` plan never overlaps the body. Nothing underfoot (jump, water, air) or ground ahead: `NONE` and nothing placed. Facing as above. `run_player_interaction_preview`'s numbers are unchanged (848/80/96/16 and 480/80/80).

**9. The headless proof, `render_feature_bridge`** (new, `tests/cuda/bridge/`, on the card). The built program cannot be used for this: `--test-world --walk` cannot put 20 planks in a hand or a lake under a body. So the proof is a test program on the real renderer (the sky rig's hand world, the same as `render_feature_preview`). It builds a lake 5 deep, with a stone platform one block wide whose top is level with the water. A real `player_t` with 24 planks walks east tick by tick as the game runs it (`player_interact_tick`, then `player_tick`), at pitch -0.5, with the right button held. The preview goes to the card as `game_preview_publish` sends it. All 20 go down in a line, each shown `OK` the tick before on its own cell; the body never drops. Blue pixels in the 640x360 first-person shots: 13059 at the start, 9205 after 5, 11145 after 10, 8456 at the end (the test needs more than 200). **Looked at:** `bridge_05.png` shows the planks underfoot and the next cell blue over the water. `bridge_20.png` shows the blue cell past the end. `bridge_side.png`, from above and to the south, shows the stone platform, the 20 planks in a straight line, and the blue cell past the end. The white disc in every first-person shot is the sun's reflection in the water. It is in the `_bare` shots too, so it is not the preview. To see them: `MC_BRIDGE_SHOTS=<dir> tools/test-or-build.sh timeout 280 ctest --preset debug -R '^render_feature_bridge$'`. It is not one of `MC_ART_TESTS`: it has no golden picture.

**10. For phase 2 (doors),** which edits the same function: the change to `player_interaction_place_plan` is three lines at its top (`hit` as a pointer, possibly to `ahead`). A door plan built from `hit` and `cell` after that works for the cell ahead with no further changes.

**11. Not verified in a live game here.** Ahmed's gate is the live check.

**12. Verification, 2026-11-10.** `tools/test-or-build.sh cmake --build --preset debug`: exit 0. `tools/test-or-build.sh timeout 1800 ctest --preset debug -R '^(run_player_interaction.*|run_player_movement|run_playthrough|standards_lint|clang_format|render_feature_preview|render_feature_bridge)$' --output-on-failure`: 10 of 11 passed the first time; `clang_format` failed on one line of the new test. After `clang-format -i`, `clang_format` and `standards_lint` passed again. `pgrep -a minecraft_c` and `pgrep -af` of this worktree's build: nothing.
